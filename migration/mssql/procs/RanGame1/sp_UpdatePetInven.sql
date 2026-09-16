

-- Return
-- DB_ERROR -1
-- DB_OK 0
CREATE PROCEDURE [dbo].[sp_UpdatePetInven]
	@nChaNum			int,
	@nPetNum		    int,
	@nPetInvenType		int,
	@nPetInvenMID		int,
	@nPetInvenSID		int,
	@nPetInvenCMID		int,
	@nPetInvenCSID		int,
	@nPetInvenAvailable	int,
	@nReturn		int OUTPUT
AS
	DECLARE
		@error_var int, 
		@rowcount_var int
		
	SET NOCOUNT ON
	
	SET @nReturn = 0	
	
	BEGIN TRAN

	-- @nPetInvenType = 1 : Accessory for A type
	-- @nPetInvenType = 2 : Accessory for B type
	-- @nPetInvenType = 3 : Skll

	If @nPetInvenType=1 OR @nPetInvenType=2
	BEGIN
		UPDATE PetInven
		SET PetInvenMID = @nPetInvenMID
		, PetInvenSID = @nPetInvenSID
		, PetInvenCMID = @nPetInvenCMID
		, PetInvenCSID = @nPetInvenCSID
		, PetInvenUpdateDate = getdate()		
		WHERE PetNum = @nPetNum And PetChaNum=@nChaNum
		And PetInvenType = @nPetInvenType
	END
	Else If @nPetInvenType=3
	BEGIN
		UPDATE PetInven
		SET PetInvenCMID = @nPetInvenCMID
		, PetInvenCSID = @nPetInvenCSID
		, PetInvenAvailable = @nPetInvenAvailable
		, PetInvenUpdateDate = getdate()		
		WHERE PetNum = @nPetNum
		And PetChaNum = @nChaNum
		And PetInvenType = @nPetInvenType
		And PetInvenMID = @nPetInvenMID
		And PetInvenSID = @nPetInvenSID
	END

	-- PetInven Update Return Result
	SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT    

	IF @error_var <> 0
	BEGIN
		ROLLBACK TRAN
		SET @nReturn = -1
	END
	ELSE
	BEGIN
		COMMIT TRAN
		SET @nReturn = 0
	END    

	IF (@error_var=0 AND @rowcount_var=0)
	BEGIN
		BEGIN TRAN		

		INSERT INTO PetInven (PetNum, PetChaNum, PetInvenType, PetInvenMID, PetInvenSID, PetInvenCMID, PetInvenCSID, PetInvenAvailable)
		Values (@nPetNum, @nChaNum, @nPetInvenType, @nPetInvenMID, @nPetInvenSID, @nPetInvenCMID, @nPetInvenCSID, @nPetInvenAvailable)

		SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT    
	
		IF @error_var <> 0 OR @rowcount_var = 0
		BEGIN
			ROLLBACK TRAN
			SET @nReturn = -1
		END
		ELSE
		BEGIN
			COMMIT TRAN
			SET @nReturn = 0
		END    
	END
	SET NOCOUNT OFF
	RETURN @nReturn


