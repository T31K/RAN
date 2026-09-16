

-- Return
-- DB_ERROR -1
-- DB_OK 0
CREATE PROCEDURE [dbo].[sp_RenamePet]
	@nChaNum	int,
	@nPetNum	int,
	@szPetName	varchar(20),
	@nReturn	int OUTPUT
AS	
	DECLARE
		@error_var int, 
		@rowcount_var int,
		@nPetNumTemp int
		
	SET NOCOUNT ON
	
	SET @nReturn = 0
	SET @nPetNumTemp = 0
	
	SELECT @nPetNumTemp=PetNum FROM PetInfo WHERE PetName=@szPetName
	
	IF @nPetNumTemp <> 0 -- Exist PET Name
	BEGIN
		SET @nReturn = -1
	END
	ELSE
	BEGIN
		BEGIN TRAN

		UPDATE PetInfo
		SET PetName=@szPetName
		WHERE PetNum=@nPetNum And PetChaNum=@nChaNum

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



