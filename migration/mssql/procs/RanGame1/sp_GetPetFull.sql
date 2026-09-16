

-- Return
-- DB_ERROR -1
-- Full amount
CREATE PROCEDURE [dbo].[sp_GetPetFull]
	@nChaNum	int,
	@nPetNum	int,	
	@nReturn	int OUTPUT
AS
	DECLARE
		@error_var int,
		@rowcount_var int,
		@PetFull int
	
	SET NOCOUNT ON
	
	SET @nReturn = 0

	SELECT @PetFull = PetFull
	FROM PetInfo
	WHERE PetNum=@nPetNum And PetChaNum=@nChaNum And PetDeleted=0	
	
	SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT    
	IF @error_var <> 0 OR @rowcount_var = 0
	BEGIN
		SET @nReturn = -1
	END
	ELSE
	BEGIN
		SET @nReturn = @PetFull
	END
	
	SET NOCOUNT OFF
	RETURN @nReturn


